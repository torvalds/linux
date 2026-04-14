.. SPDX-License-Identifier: GPL-2.0

.. _pt_process_howto:

COMO FAZER o desenvolvimento do kernel Linux
============================================

Este é o documento definitivo sobre este tópico. Ele contém instruções
sobre como se tornar um desenvolvedor do kernel Linux e como aprender a
trabalhar com a comunidade de desenvolvimento do kernel Linux. Ele tenta
não conter nada relacionado aos aspectos técnicos da programação do kernel,
mas ajudará a apontar a direção certa para isso.

Se algo neste documento ficar desatualizado, por favor, envie patches para
o mantenedor deste arquivo, que está listado no final do documento.


Introdução
------------

Então, você quer aprender como se tornar um desenvolvedor do kernel Linux?
Ou o seu gerente lhe disse: "Vá escrever um driver Linux para este
dispositivo". O objetivo deste documento é ensinar tudo o que você precisa
saber para conseguir isso, descrevendo o processo pelo qual você deve passar
e oferecendo dicas sobre como trabalhar com a comunidade. Ele também tentará
explicar algumas das razões pelas quais a comunidade trabalha da forma que
trabalha.

O kernel é escrito principalmente em C, com algumas partes dependentes de
arquitetura escritas em assembly. Um bom entendimento de C é necessário para
o desenvolvimento do kernel. O conhecimento de Assembly (de qualquer
arquitetura) não é obrigatório, a menos que você planeje fazer
desenvolvimento de baixo nível para essa arquitetura específica. Embora não
sejam um substituto para uma formação sólida em C e/ou anos de experiência,
os seguintes livros são bons para, no mínimo, referência:

  - "The C Programming Language" por Kernighan e Ritchie [Prentice Hall]

  - "Practical C Programming" por Steve Oualline [O'Reilly]

  - "C: A Reference Manual" por Harbison e Steele [Prentice Hall]

O kernel é escrito usando o GNU C e a GNU toolchain. Embora ele siga o
padrão ISO C11, ele utiliza uma série de extensões que não estão presentes
no padrão. O kernel é um ambiente C independente (freestanding), sem
dependência da biblioteca C padrão (libc), portanto, algumas partes do
padrão C não são suportadas. Divisões arbitrárias de "long long" e ponto
flutuante não são permitidas. Às vezes, pode ser difícil entender as
suposições que o kernel faz sobre a toolchain e as extensões que ele utiliza
e, infelizmente, não existe uma referência definitiva para elas. Por favor,
verifique as páginas de informações do gcc (`info gcc`) para obter algumas
informações sobre elas.

Por favor, lembre-se de que você está tentando aprender como trabalhar com a
comunidade de desenvolvimento existente. É um grupo diversificado de pessoas,
com altos padrões de codificação, estilo e procedimento. Esses padrões foram
criados ao longo do tempo com base no que se descobriu funcionar melhor para
uma equipe tão grande e geograficamente dispersa. Tente aprender o máximo
possível sobre esses padrões com antecedência, pois eles estão bem
documentados; não espere que as pessoas se adaptem a você ou à forma de fazer
as coisas da sua empresa.


Questões Legais
---------------

O código-fonte do kernel Linux é lançado sob a GPL. Por favor, veja o arquivo
COPYING no diretório principal da árvore de fontes. As regras de licenciamento
do kernel Linux e como usar os identificadores `SPDX <https://spdx.org/>`_ no
código-fonte estão descritas em :ref:`Documentation/process/license-rules.rst <kernel_licensing>`.
Se você tiver mais perguntas sobre a licença, por favor, entre em contato com
um advogado e não pergunte na lista de discussão do kernel Linux. As pessoas
nas listas de discussão não são advogados e você não deve confiar em suas
declarações sobre assuntos jurídicos.

Para perguntas e respostas comuns sobre a GPL, por favor, veja:

        https://www.gnu.org/licenses/gpl-faq.html


Documentação
------------

A árvore de fontes do kernel Linux possui uma vasta gama de documentos que
são inestimáveis para aprender como interagir com a comunidade do kernel.
Quando novos recursos são adicionados ao kernel, recomenda-se que novos
arquivos de documentação também sejam adicionados explicando como usar o
recurso. Quando uma mudança no kernel faz com que a interface que o kernel
expõe para o espaço do usuário (userspace) mude, recomenda-se que você envie
a informação ou um patch para as páginas de manual explicando a mudança para
o mantenedor das páginas de manual em alx@kernel.org, e coloque em cópia (CC)
a lista linux-api@vger.kernel.org.

Aqui está uma lista de arquivos que estão na árvore de fontes do kernel e
que são de leitura obrigatória:

  :ref:`Documentation/admin-guide/README.rst <readme>`
    Este arquivo fornece um breve histórico sobre o kernel Linux e descreve
    o que é necessário fazer para configurar e compilar o kernel. Pessoas
    que são novas no kernel devem começar por aqui.

  :doc:`changes`
    Este arquivo fornece uma lista das versões mínimas de vários pacotes de
    software que são necessários para compilar e executar o kernel com
    sucesso.

  :ref:`Documentation/process/coding-style.rst <codingstyle>`
    Este documento descreve o estilo de codificação do kernel Linux e parte
    da fundamentação por trás dele. Espera-se que todo código novo siga as
    diretrizes deste documento. A maioria dos mantenedores apenas aceitará
    patches se essas regras forem seguidas, e muitas pessoas apenas
    revisarão o código se ele estiver no estilo adequado.

  :ref:`Documentation/process/submitting-patches.rst <submittingpatches>`
    Este arquivo descreve em detalhes explícitos como criar e enviar
    um patch com sucesso, incluindo (mas não limitado a):

        - Conteúdo do e-mail
        - Formato do e-mail
        - Para quem enviá-lo

    Seguir estas regras não garantirá o sucesso (já que todos os patches
    estão sujeitos a um escrutínio de conteúdo e estilo), mas não segui-las
    quase sempre o impedirá.

Outras excelentes descrições de como criar patches adequadamente são:

	"O Patch Perfeito"
		https://www.ozlabs.org/~akpm/stuff/tpp.txt

	"Formato de Submissão de Patch do Kernel Linux"
		https://web.archive.org/web/20180829112450/http://linux.yyz.us/patch-format.html

  :ref:`Documentation/process/stable-api-nonsense.rst <stable_api_nonsense>`
    Este arquivo descreve a justificativa por trás da decisão consciente de
    não ter uma API estável dentro do kernel, incluindo pontos como:

      - Camadas de adaptação (shim-layers) de subsistemas (para compatibilidade?)
      - Portabilidade de drivers entre sistemas operacionais.
      - Mitigação de mudanças rápidas dentro da árvore de fontes do kernel
        (ou impedimento de mudanças rápidas).

    Este documento é crucial para compreender a filosofia de desenvolvimento
    do Linux e é muito importante para pessoas que estão migrando para o
    Linux vindas do desenvolvimento em outros Sistemas Operacionais.

  :ref:`Documentation/process/security-bugs.rst <securitybugs>`
    Se você acredita ter encontrado um problema de segurança no kernel Linux,
    por favor, siga os passos descritos neste documento para ajudar a
    notificar os desenvolvedores do kernel e auxiliar na resolução do problema.

  :ref:`Documentation/process/management-style.rst <managementstyle>`
    Este documento descreve como os mantenedores do kernel Linux operam e o
    ethos compartilhado por trás de suas metodologias. Esta é uma leitura
    importante para qualquer pessoa nova no desenvolvimento do kernel (ou
    para qualquer pessoa simplesmente curiosa sobre isso), pois resolve muitos
    equívocos comuns e confusões sobre o comportamento único dos mantenedores
    do kernel.

  :ref:`Documentation/process/stable-kernel-rules.rst <stable_kernel_rules>`
    Este arquivo descreve as regras sobre como ocorrem os lançamentos das
    versões estáveis (stable) do kernel e o que fazer se você desejar que
    uma alteração seja incluída em um desses lançamentos.

  :ref:`Documentation/process/kernel-docs.rst <kernel_docs>`
    Uma lista de documentação externa que pertence ao desenvolvimento do
    kernel. Por favor, consulte esta lista caso não encontre o que está
    procurando dentro da documentação interna do kernel.

  :ref:`Documentation/process/applying-patches.rst <applying_patches>`
    Uma boa introdução descrevendo exatamente o que é um patch e como
    aplicá-lo aos diferentes ramos (branches) de desenvolvimento do kernel.

O kernel também possui um grande número de documentos que podem ser
gerados automaticamente a partir do próprio código-fonte ou de
marcações ReStructuredText (ReST), como esta. Isso inclui uma
descrição completa da API interna do kernel e regras sobre como
manipular o bloqueio (locking) corretamente.

Todos esses documentos podem ser gerados em formato PDF ou HTML ao
executar::

	make pdfdocs
	make htmldocs

respectivamente, a partir do diretório principal do código-fonte do kernel.

Os documentos que utilizam a marcação ReST serão gerados em
Documentation/output. Eles também podem ser gerados nos formatos
LaTeX e ePub com::

	make latexdocs
	make epubdocs

Como se tornar um desenvolvedor do kernel
------------------------------------------

Se você não sabe nada sobre o desenvolvimento do kernel Linux, você deve
consultar o projeto Linux KernelNewbies:

	https://kernelnewbies.org

Ele consiste em uma lista de discussão útil onde você pode fazer quase
qualquer tipo de pergunta básica sobre o desenvolvimento do kernel
(certifique-se de pesquisar nos arquivos primeiro, antes de perguntar
algo que já foi respondido no passado). Ele também possui um canal de
IRC que você pode usar para fazer perguntas em tempo real, e muita
documentação útil para aprender sobre o desenvolvimento do kernel Linux.

O site possui informações básicas sobre a organização do código,
subsistemas e projetos atuais (tanto in-tree quanto out-of-tree).
Também descreve algumas informações logísticas básicas, como por exemplo,
como compilar um kernel e aplicar um patch.

Se você não sabe por onde começar, mas deseja procurar alguma tarefa
para iniciar sua integração na comunidade de desenvolvimento do kernel,
acesse o projeto Linux Kernel Janitor:

	https://kernelnewbies.org/KernelJanitors

É um ótimo lugar para começar. Ele descreve uma lista de problemas
relativamente simples que precisam ser limpos e corrigidos dentro da
árvore de códigos-fonte do kernel Linux. Ao trabalhar com os
desenvolvedores responsáveis por este projeto, você aprenderá o básico
sobre como incluir seu patch na árvore do kernel Linux e,
possivelmente, será orientado sobre o que trabalhar em seguida, caso
ainda não tenha uma ideia.

Antes de fazer qualquer modificação real no código do kernel Linux, é
imperativo entender como o código em questão funciona. Para esse
propósito, nada é melhor do que lê-lo diretamente (a maioria das partes
complexas está bem comentada), talvez até com a ajuda de ferramentas
especializadas. Uma ferramenta particularmente recomendada é o projeto
Linux Cross-Reference, que é capaz de apresentar o código-fonte em um
formato de página web indexada e auto-referenciada. Um excelente
repositório atualizado do código do kernel pode ser encontrado em:

    https://elixir.bootlin.com/


O processo de desenvolvimento
-----------------------------

O processo de desenvolvimento do kernel Linux consiste atualmente em algumas
"branches" (ramos) principais diferentes e muitos ramos de subsistemas
específicos. Esses diferentes ramos são:

  - Árvore principal (mainline) do Linus
  - Várias árvores estáveis com múltiplos números de versão principal
  - Árvores específicas de subsistemas
  - Árvore de testes de integração linux-next

Árvore principal (Mainline tree)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A árvore principal é mantida por Linus Torvalds e pode ser encontrada em
https://kernel.org ou no repositório. Seu processo de desenvolvimento é
o seguinte:

  - Assim que um novo kernel é lançado, uma janela de duas semanas é aberta;
    durante esse período, os mantenedores podem enviar grandes diffs para
    Linus, geralmente patches que já foram incluídos na linux-next por algumas
    semanas. A forma preferida de enviar grandes mudanças é usando o git
    (a ferramenta de gerenciamento de código-fonte do kernel, mais informações
    podem ser encontradas em https://git-scm.com/), mas patches simples
    também são aceitos.
  - Após duas semanas, um kernel -rc1 é lançado e o foco passa a ser tornar
    o novo kernel o mais sólido possível. A maioria dos patches neste estágio
    deve corrigir uma regressão. Bugs que sempre existiram não são regressões,
    portanto, envie esses tipos de correções apenas se forem importantes.
    Observe que um driver (ou sistema de arquivos) totalmente novo pode ser
    aceito após o -rc1 porque não há risco de causar regressões com tal
    mudança, desde que a alteração seja autocontida e não afete áreas fora do
    código que está sendo adicionado. O git pode ser usado para enviar
    patches para Linus após o lançamento do -rc1, mas os patches também
    precisam ser enviados para uma lista de discussão pública para revisão.
  - Um novo -rc é lançado sempre que Linus considerar que a árvore git atual
    está em um estado razoavelmente estável e adequado para testes. O objetivo
    é lançar um novo kernel -rc a cada semana.
  - O processo continua até que o kernel seja considerado "pronto"; o
    processo deve durar cerca de 6 semanas.

Vale a pena mencionar o que Andrew Morton escreveu na lista de discussão
do kernel Linux sobre os lançamentos do kernel:

    *"Ninguém sabe quando um kernel será lançado, porque ele é
    lançado de acordo com o status percebido dos bugs, não de acordo
    com um cronograma pré-concebido."*

Várias árvores estáveis com múltiplos números de versão principal
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Kernels com versões de 3 partes são kernels -stable (estáveis). Eles
contêm correções relativamente pequenas e críticas para problemas de
segurança ou regressões significativas descobertas em um determinado
lançamento principal da árvore mainline. Cada lançamento em uma série
estável principal incrementa a terceira parte do número da versão,
mantendo as duas primeiras partes iguais.

Este é o ramo recomendado para usuários que desejam o kernel estável
mais recente e não estão interessados em ajudar a testar versões de
desenvolvimento ou experimentais.

As árvores estáveis são mantidas pela equipe "stable"
<stable@vger.kernel.org> e são lançadas conforme a necessidade exigir.
O período normal de lançamento é de aproximadamente duas semanas, mas
pode ser mais longo se não houver problemas urgentes. Por outro lado,
um problema relacionado à segurança pode fazer com que um lançamento
ocorra quase instantaneamente.

O arquivo :ref:`Documentation/process/stable-kernel-rules.rst <stable_kernel_rules>`
na árvore do kernel documenta quais tipos de mudanças são aceitáveis para
a árvore -stable e como o processo de lançamento funciona.

Árvores específicas de subsistemas
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Os mantenedores dos vários subsistemas do kernel — e também muitos
desenvolvedores de subsistemas do kernel — expõem seu estado atual de
desenvolvimento em repositórios de código-fonte. Dessa forma, outros
podem ver o que está acontecendo nas diferentes áreas do kernel. Em
áreas onde o desenvolvimento é rápido, um desenvolvedor pode ser
solicitado a basear suas submissões em tal árvore de subsistema do
kernel para que conflitos entre a submissão e outros trabalhos já em
andamento sejam evitados.

A maioria desses repositórios são árvores git, mas também existem outros
SCMs em uso, ou filas de patches sendo publicadas como séries quilt. Os
endereços desses repositórios de subsistemas estão listados no arquivo
MAINTAINERS. Muitos deles podem ser navegados em https://git.kernel.org/.

Antes que um patch proposto seja incluído em tal árvore de subsistema,
ele está sujeito a uma revisão que ocorre principalmente em listas de
discussão (veja a seção respectiva abaixo). Para vários subsistemas do
kernel, este processo de revisão é rastreado com a ferramenta patchwork.
O Patchwork oferece uma interface web que mostra as postagens de patches,
quaisquer comentários sobre um patch ou revisões feitas a ele, e os
mantenedores podem marcar os patches como "sob revisão", "aceitos" ou
"rejeitados". A maioria desses sites patchwork está listada em
https://patchwork.kernel.org/.

Árvore de testes de integração linux-next
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Antes que as atualizações das árvores de subsistemas sejam mescladas na
árvore mainline, elas precisam ser testadas quanto à integração. Para
este propósito, existe um repositório de testes especial no qual
praticamente todas as árvores de subsistemas são integradas (pulled)
quase diariamente:

    https://git.kernel.org/pub/scm/linux/kernel/git/next/linux-next.git

Dessa forma, a linux-next oferece uma visão resumida do que se espera
que entre no kernel mainline no próximo período de mesclagem (merge
window). Testadores aventureiros são muito bem-vindos para testar a
linux-next em tempo de execução.


Relato de Bugs
--------------

O arquivo 'Documentation/admin-guide/reporting-issues.rst' no diretório
principal de códigos-fonte do kernel descreve como relatar um possível
bug no kernel e detalha que tipo de informação é necessária para os
desenvolvedores do kernel ajudarem a rastrear o problema.

Gerenciando relatos de bugs
---------------------------

Uma das melhores maneiras de colocar em prática suas habilidades de hacking
é corrigindo bugs relatados por outras pessoas. Você não apenas ajudará a
tornar o kernel mais estável, mas também aprenderá a resolver problemas do
mundo real, melhorará suas habilidades e outros desenvolvedores passarão a
notar sua presença. Corrigir bugs é uma das melhores formas de obter mérito
entre outros desenvolvedores, pois poucas pessoas gostam de gastar tempo
corrigindo bugs de terceiros.

Para trabalhar em relatos de bugs já existentes, encontre um subsistema no
qual você esteja interessado. Verifique no arquivo MAINTAINERS para onde
os bugs daquele subsistema são relatados; geralmente será uma lista de
discussão, raramente um rastreador de bugs (bugtracker). Pesquise nos
arquivos de mensagens do local indicado por relatos recentes e ajude onde
achar apropriado. Você também pode verificar o site
https://bugzilla.kernel.org para relatos de bugs; apenas alguns
subsistemas do kernel o utilizam ativamente para relato ou rastreamento,
entretanto, bugs de todo o kernel acabam sendo registrados lá.


Listas de discussão
-------------------

Como alguns dos documentos acima descrevem, a maioria dos desenvolvedores
do núcleo (core) do kernel participa da Linux Kernel Mailing List (LKML).
Detalhes sobre como se inscrever e cancelar a inscrição na lista podem
ser encontrados em:

    https://subspace.kernel.org/subscribing.html

Existem arquivos de mensagens da lista na web em muitos lugares diferentes.
Use um mecanismo de busca para encontrar esses arquivos. Por exemplo:

    https://lore.kernel.org/linux-kernel/

É altamente recomendável que você pesquise nos arquivos sobre o tópico que
deseja abordar antes de postar na lista. Muitas coisas já discutidas em
detalhes estão registradas apenas nos arquivos das listas de discussão.

A maioria dos subsistemas individuais do kernel também possui sua própria
lista de discussão separada, onde realizam seus esforços de desenvolvimento.
Consulte o arquivo MAINTAINERS para obter uma lista de quais são essas
listas para os diferentes grupos.

Muitas das listas estão hospedadas no kernel.org. Informações sobre elas
podem ser encontradas em:

    https://subspace.kernel.org

Por favor, lembre-se de seguir bons hábitos de comportamento ao usar as
listas. Embora um pouco clichê, a URL a seguir possui algumas diretrizes
simples para interagir com a lista (ou qualquer outra lista):

    https://subspace.kernel.org/etiquette.html

Se várias pessoas responderem ao seu e-mail, a lista de destinatários em
CC: pode se tornar bem grande. Não remova ninguém da lista CC: sem um
bom motivo, e não responda apenas para o endereço da lista. Acostume-se
a receber o e-mail duas vezes (um do remetente e outro da lista) e não
tente ajustar isso adicionando cabeçalhos de e-mail complexos; as pessoas
não gostarão disso.

Lembre-se de manter o contexto e a atribuição de suas respostas intactos;
mantenha as linhas do tipo "John Kernelhacker escreveu...:" no topo da
sua resposta e adicione seus comentários entre as seções citadas
individualmente, em vez de escrever tudo no topo do e-mail.

Se você adicionar patches ao seu e-mail, certifique-se de que sejam texto
puro legível, conforme declarado em
:ref:`Documentation/process/submitting-patches.rst <submittingpatches>`.
Os desenvolvedores do kernel não querem lidar com anexos ou patches
compactados; eles podem querer comentar linhas individuais do seu patch,
o que só funciona dessa forma. Certifique-se de usar um programa de
e-mail que não altere espaços e caracteres de tabulação (tabs). Um bom
primeiro teste é enviar o e-mail para si mesmo e tentar aplicar o seu
próprio patch. Se isso não funcionar, conserte seu programa de e-mail ou
troque-o até que funcione.

Acima de tudo, por favor, lembre-se de mostrar respeito aos outros
inscritos.


Trabalhando com a comunidade
----------------------------

O objetivo da comunidade do kernel é fornecer o melhor kernel possível.
Quando você envia um patch para aceitação, ele será revisado por seus
méritos técnicos e apenas por eles. Então, o que você deve esperar?

  - críticas
  - comentários
  - solicitações de mudança
  - solicitações de justificativa
  - silêncio

Lembre-se, isso faz parte do processo de incluir seu patch no kernel.
Você deve ser capaz de aceitar críticas e comentários sobre seus patches,
avaliá-los em nível técnico e retrabalhar seus patches ou fornecer
raciocínios claros e concisos sobre o porquê de certas mudanças não
deverem ser feitas. Se não houver respostas à sua postagem, aguarde
alguns dias e tente novamente; às vezes, as coisas se perdem no enorme
volume de mensagens.

O que você não deve fazer?

  - esperar que seu patch seja aceito sem questionamentos
  - tornar-se defensivo
  - ignorar comentários
  - reenviar o patch sem fazer nenhuma das alterações solicitadas

Em uma comunidade que busca a melhor solução técnica possível, sempre
haverá opiniões divergentes sobre o quão benéfico é um patch. Você deve
ser cooperativo e estar disposto a adaptar sua ideia para que ela se
encaixe no kernel. Ou, pelo menos, estar disposto a provar que sua ideia
vale a pena. Lembre-se: estar errado é aceitável, desde que você esteja
disposto a trabalhar em direção a uma solução correta.

É normal que as respostas ao seu primeiro patch sejam apenas uma lista
de uma dúzia de coisas que você deve corrigir. Isso **não** implica que
seu patch não será aceito e **não** é algo pessoal contra você. Simplesmente
corrija todos os problemas apontados em seu patch e envie-o novamente.


Diferenças entre a comunidade do kernel e estruturas corporativas
-----------------------------------------------------------------

A comunidade do kernel trabalha de forma diferente da maioria dos ambientes
tradicionais de desenvolvimento corporativo. Aqui está uma lista de coisas
que você pode tentar fazer para evitar problemas:

  Boas coisas a dizer em relação às suas mudanças propostas:

    - "Isto resolve múltiplos problemas."
    - "Isto remove 2000 linhas de código."
    - "Aqui está um patch que explica o que estou tentando descrever."
    - "Eu testei isso em 5 arquiteturas diferentes..."
    - "Aqui está uma série de pequenos patches que..."
    - "Isto aumenta a performance em máquinas comuns..."

  Coisas ruins que você deve evitar dizer:

    - "Nós fizemos desta forma no AIX/ptx/Solaris, portanto deve ser bom..."
    - "Eu faço isso há 20 anos, então..."
    - "Isto é necessário para minha empresa ganhar dinheiro."
    - "Isto é para nossa linha de produtos Enterprise."
    - "Aqui está meu documento de design de 1000 páginas que descreve minha ideia."
    - "Estou trabalhando nisso há 6 meses..."
    - "Aqui está um patch de 5000 linhas que..."
    - "Eu reescrevi toda a bagunça atual, e aqui está..."
    - "Eu tenho um prazo (deadline), e este patch precisa ser aplicado agora."

Outra forma em que a comunidade do kernel difere da maioria dos ambientes
tradicionais de engenharia de software é a natureza anônima da interação.
Um benefício de usar e-mail e IRC como as principais formas de comunicação
é a ausência de discriminação baseada em gênero ou raça. O ambiente de
trabalho do kernel Linux aceita mulheres e minorias porque tudo o que você
é, é um endereço de e-mail. O aspecto internacional também ajuda a nivelar
o campo de jogo porque você não pode adivinhar o gênero com base no nome
de uma pessoa. Um homem pode se chamar Andrea e uma mulher pode se chamar
Pat. A maioria das mulheres que trabalharam no kernel Linux e expressaram
uma opinião tiveram experiências positivas.

A barreira do idioma pode causar problemas para algumas pessoas que não
se sentem confortáveis com o inglês. Um bom domínio do idioma pode ser
necessário para transmitir ideias adequadamente nas listas de discussão,
por isso recomenda-se que você verifique seus e-mails para garantir que
façam sentido em inglês antes de enviá-los.


Divida suas alterações
----------------------

A comunidade do kernel Linux não aceita de bom grado grandes blocos de
código jogados de uma só vez. As mudanças precisam ser devidamente
introduzidas, discutidas e divididas em porções minúsculas e individuais.
Isso é quase o exato oposto do que as empresas costumam fazer. Sua proposta
também deve ser introduzida muito cedo no processo de desenvolvimento, para
que você possa receber feedback sobre o que está fazendo. Isso também permite
que a comunidade sinta que você está trabalhando com eles, e não simplesmente
usando-os como um depósito para sua funcionalidade. No entanto, não envie
50 e-mails de uma só vez para uma lista de discussão; sua série de patches
deve ser menor que isso quase sempre.

As razões para dividir as coisas são as seguintes:

1) Patches pequenos aumentam a probabilidade de serem aplicados, pois não
   exigem muito tempo ou esforço para verificar sua correção. Um patch de
   5 linhas pode ser aplicado por um mantenedor com apenas um olhar rápido.
   No entanto, um patch de 500 linhas pode levar horas para ser revisado
   (o tempo que leva é exponencialmente proporcional ao tamanho do patch,
   ou algo assim).

   Patches pequenos também tornam muito fácil a depuração (debug) quando
   algo dá errado. É muito mais fácil reverter patches um por um do que
   dissecar um patch muito grande após ele ter sido aplicado (e quebrado algo).

2) É importante não apenas enviar patches pequenos, mas também reescrever
   e simplificar (ou simplesmente reordenar) os patches antes de submetê-los.

Aqui está uma analogia do desenvolvedor do kernel Al Viro:

    *"Pense em um professor corrigindo o dever de casa de um aluno de
    matemática. O professor não quer ver as tentativas e erros do aluno
    antes de chegar à solução. Ele quer ver a resposta mais limpa e
    elegante. Um bom aluno sabe disso e nunca enviaria seu trabalho
    intermediário antes da solução final.*

    *O mesmo vale para o desenvolvimento do kernel. Os mantenedores e
    revisores não querem ver o processo de pensamento por trás da solução
    do problema que se está resolvendo. Eles querem ver uma solução
    simples e elegante."*

Pode ser desafiador manter o equilíbrio entre apresentar uma solução
elegante e trabalhar em conjunto com a comunidade discutindo seu trabalho
inacabado. Portanto, é bom entrar no processo cedo para obter feedback e
melhorar seu trabalho, mas também manter suas alterações em pequenos blocos
que possam ser aceitos, mesmo quando sua tarefa completa ainda não esteja
pronta para inclusão.

Também entenda que não é aceitável enviar patches para inclusão que estejam
inacabados e que serão "consertados mais tarde".


Justifique sua alteração
------------------------

Além de dividir seus patches, é muito importante que você deixe a comunidade
Linux saber por que eles deveriam adicionar essa mudança. Novas
funcionalidades devem ser justificadas como necessárias e úteis.


Documente sua alteração
-----------------------

Ao enviar seus patches, preste atenção especial ao que você diz no texto
do seu e-mail. Essas informações se tornarão as informações do ChangeLog
para o patch e serão preservadas para que todos vejam para sempre. Elas
devem descrever o patch completamente, contendo:

  - por que a mudança é necessária
  - a abordagem geral de design no patch
  - detalhes de implementação
  - resultados de testes

Para mais detalhes sobre como tudo isso deve ser, por favor, veja a seção
ChangeLog do documento:

  "O Patch Perfeito"
      https://www.ozlabs.org/~akpm/stuff/tpp.txt

Todas essas coisas às vezes são muito difíceis de fazer. Pode levar anos
para aperfeiçoar essas práticas (se é que é possível). É um processo
contínuo de melhoria que exige muita paciência e determinação. Mas não
desista, é possível. Muitos fizeram isso antes, e cada um teve que começar
exatamente onde você está agora.

----------

Agradecimentos a Paolo Ciarrocchi, que permitiu que a seção "Processo de
Desenvolvimento" (https://lwn.net/Articles/94386/) fosse baseada em um
texto que ele escreveu, e a Randy Dunlap e Gerrit Huizenga por parte da
lista de coisas que você deve ou não dizer. Também agradecemos a Pat Mochel,
Hanna Linder, Randy Dunlap, Kay Sievers, Vojtech Pavlik, Jan Kara,
Josh Boyer, Kees Cook, Andrew Morton, Andi Kleen, Vadim Lobanov, Jesper Juhl,
Adrian Bunk, Keri Harris, Frans Pop, David A. Wheeler, Junio Hamano,
Michael Kerrisk e Alex Shepard por suas revisões, comentários e contribuições.
Sem a ajuda deles, este documento não teria sido possível.

Mantenedor: Greg Kroah-Hartman <greg@kroah.com>
