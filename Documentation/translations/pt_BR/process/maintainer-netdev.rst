.. SPDX-License-Identifier: GPL-2.0

=====================================
Subsistema de Rede do Linux (netdev)
=====================================

tl;dr
-----

- **Direcione seu patch para uma árvore** – use ``[PATCH net]``para correções
  ou ``[PATCH net-next]`` para novas funcionalidades.
- **Tag Fixes** – para correções, a tag ``Fixes:`` é obrigatória,
  independentemente da árvore de destino.
- **Tamanho da série** – não envie séries grandes (> 15 patches);divida-as em
  partes menores.
- **Intervalo de envio** – não reenvie seus patches dentro de um período de 24
  horas.
- **Reverse xmas tree** – organize as declarações de variáveis locais da mais
  longa para a mais curta.

netdev
------
A **netdev** é a lista de discussão para todos os assuntos do Linux relacionados
a rede. Isso inclui qualquer item encontrado em ``net/`` (ex: código principal
como IPv6) e  em ``drivers/net`` (ex: drivers específicos de hardware) na árvore
de diretórios do Linux.

Note que alguns subsistemas (ex: drivers de rede sem fio/wireless), que possuem
um  alto volume de tráfego, possuem suas próprias listas de discussão e árvores
específicas.

Como muitas outras listas de discussão do Linux, a lista netdev é hospedada no
`kernel.org <https://www.kernel.org/>`_, com arquivos disponíveis em
https://lore.kernel.org/netdev/.

À exceção dos subsistemas mencionados anteriormente, todo o desenvolvimento de
rede  do Linux (ex: RFCs, revisões, comentários, etc.) ocorre na **netdev**.

Ciclo de Desenvolvimento
------------------------

Aqui está um pouco de informação contextual sobre a cadência de desenvolvimento
do Linux. Cada nova versão (release) inicia-se com uma "janela de mesclagem"
(*merge window*) de duas semanas, onde os mantenedores principais enviam suas
novas implementações para o Linus para incorporação na árvore principal
(*mainline tree*).

Após as duas semanas, a janela de mesclagem é fechada e a versão é
nomeada/etiquetada  como ``-rc1``. Nenhuma funcionalidade nova é incorporada à
árvore principal após  isso -- espera-se apenas correções (*fixes*) para o
conteúdo da rc1.

Após cerca de uma semana coletando correções para a rc1, a rc2 é lançada. Isso
se  repete semanalmente até a rc7 (tipicamente; às vezes rc6 se o ritmo estiver
calmo, ou rc8 se houver muita instabilidade); uma semana após a última vX.Y-rcN
ser  concluída, a versão oficial vX.Y é lançada.

Para descobrir em que ponto do ciclo estamos agora - carregue a página da
mainline (Linus) aqui:

  https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git

e observe o topo da seção de "tags". Se for rc1, estamos no início do ciclo
de desenvolvimento. Se a rc7 foi marcada há uma semana, então um lançamento
é provavelmente iminente. Se a tag mais recente for uma tag de lançamento
final (sem o sufixo ``-rcN``) - muito provavelmente estamos em uma janela de
mesclagem (*merge window*) e o ``net-next`` está fechado.

Árvores git e fluxo de patches
------------------------------

Existem duas árvores de rede (repositórios git) em jogo. Ambas são coordenadas
por David Miller, o mantenedor principal de rede. Há a árvore ``net``e a árvore
``net-next``. Como você provavelmente pode adivinhar pelos nomes, a árvore
``net`` é para correções de código existente já na árvore mainline de Linus, e a
``net-next`` é para onde o novo código vai para o lançamento futuro.
Você pode encontrar as árvores aqui:

- https://git.kernel.org/pub/scm/linux/kernel/git/netdev/net.git
- https://git.kernel.org/pub/scm/linux/kernel/git/netdev/net-next.git

Relacionando isso ao desenvolvimento do kernel: no início da janela de mesclagem
(*merge window*) de 2 semanas, a árvore ``net-next`` será fechada, sem novas
mudanças ou funcionalidades. O conteúdo novo acumulado nas últimas 10 semanas
será passado para a mainline/Linus via um *pull request* para a vX.Y ao mesmo
tempo, a árvore ``net`` começará a acumular correções para este conteúdo enviado
relacionado à vX.Y.

Um anúncio indicando quando a ``net-next`` foi fechada é geralmente enviado para
a netdev, mas sabendo o que foi dito acima, você pode prever isso com
antecedência.

.. warning::

  Não envie novo conteúdo para a ``net-next`` para a netdev durante o período
  em que a árvore ``net-next`` estiver fechada.

Patches RFC enviados apenas para revisão são obviamente bem-vindos a qualquer
momento (use ``--subject-prefix='RFC net-next'`` com ``git format-patch``).

Pouco depois das duas semanas terem passado (e a vX.Y-rc1 ser lançada), a árvore
para a ``net-next`` reabre para coletar conteúdo para o próximo lançamento
(vX.Y+1).

Se você não estiver inscrito na netdev e/ou simplesmente não tiver certeza se a
``net-next`` já reabriu, basta verificar o link do repositório git da
``net-next`` acima para quaisquer novos *commits* relacionados à rede. Você
também pode verificar o seguinte site para o status atual:

  https://netdev.bots.linux.dev/net-next.html

A árvore ``net`` continua a coletar correções para o conteúdo da vX.Y e é
enviada de volta para Linus em intervalos regulares (~semanais). Isso significa
que o foco da ``net`` é a estabilização e correções de bugs.

Finalmente, a vX.Y é lançada e todo o ciclo recomeça.

Revisão de patches da netdev
----------------------------

Status do patch
~~~~~~~~~~~~~~~

O status de um patch pode ser verificado olhando a fila principal do patchwork
para a netdev:

  https://patchwork.kernel.org/project/netdevbpf/list/

O campo "State" informará exatamente onde as coisas estão com o seu patch:

=================  ============================================================
Estado do patch    Descrição
=================  ============================================================
New, Under review  revisão pendente, o patch está na fila do mantenedor
                   para revisão; os dois estados são usados alternadamente
                   (dependendo do co-mantenedor exato que estiver lidando
                   com o patchwork no momento)
Accepted           o patch foi aplicado à árvore de rede apropriada,
                   isso é geralmente definido de forma automática pelo pw-bot
Needs ACK          aguardando um "ack" de um especialista da área
                   ou testes
Changes requested  o patch não passou na revisão, espera-se uma nova
                   revisão com mudanças apropriadas no código e na mensagem
                   de commit
Rejected           o patch foi rejeitado e não se espera uma nova
                   revisão
Not applicable     espera-se que o patch seja aplicado fora do
                   subsistema de rede
Awaiting upstream  o patch deve ser revisado e tratado pelo sub-mantenedor
                   apropriado, que o enviará para as árvores de rede;
                   patches definidos como ``Awaiting upstream`` no patchwork
                   da netdev geralmente permanecerão neste estado,
                   independentemente de o sub-mantenedor ter solicitado
                   mudanças, aceito ou rejeitado o patch
Deferred           o patch precisa ser reenviado mais tarde, geralmente
                   devido a alguma dependência ou porque foi enviado para
                   uma árvore fechada
Superseded         uma nova versão do patch foi enviada, geralmente
                   definido pelo pw-bot
RFC                não deve ser aplicado, geralmente não está na
                   fila de revisão do mantenedor; o pw-bot pode definir
                   patches para este estado automaticamente com base nas
                   tags do assunto
=================  ============================================================

Os patches são indexados pelo cabeçalho ``Message-ID`` dos e-mails que os
transportaram; portanto, se você tiver problemas para encontrar seu patch,
anexe o valor do ``Message-ID`` à URL acima.

Atualizando o status do patch
-----------------------------

Colaboradores e revisores não têm permissões para atualizar o estado do patch
diretamente no patchwork. O Patchwork não expõe muitas informações sobre o
histórico do estado dos patches; portanto, ter várias pessoas atualizando o
estado leva a confusões.

Em vez de delegar permissões do patchwork, a netdev usa um robô de e-mail
simples (bot) que procura por comandos/linhas especiais dentro dos e-mails
enviados para a lista de discussão. Por exemplo, para marcar uma série como
Mudanças Solicitadas (*Changes Requested*), é necessário enviar a seguinte
linha em qualquer lugar na thread do e-mail::

  pw-bot: changes-requested

Como resultado, o bot definirá toda a série como Mudanças Solicitadas. Isso
pode ser útil quando o autor descobre um bug em sua própria série e deseja
evitar que ela seja aplicada.

O uso do bot é totalmente opcional; em caso de dúvida, ignore completamente a
existência dele. Os mantenedores classificarão e atualizarão o estado dos
patches por conta própria. Nenhum e-mail deve ser enviado à lista com o
propósito principal de se comunicar com o bot; os comandos do bot devem ser
vistos como metadados.

O uso do bot é restrito aos autores dos patches (o cabeçalho ``From:`` no envio
do patch e no comando deve coincidir!), mantenedores do código modificado de
acordo com o arquivo MAINTAINERS (novamente, o ``From:`` deve coincidir
com a entrada no MAINTAINERS) e alguns revisores seniores.

O bot registra sua atividade aqui:

  https://netdev.bots.linux.dev/pw-bot.html

Prazos de revisão
~~~~~~~~~~~~~~~~~

De modo geral, os patches são triados rapidamente (em menos de 48h). Mas
seja paciente; se o seu patch estiver ativo no patchwork (ou seja, listado
na lista de patches do projeto), as chances de ele ter sido esquecido são
próximas de zero.

O alto volume de desenvolvimento na netdev faz com que os revisores encerrem
discussões de forma relativamente rápida. É muito improvável que novos
comentários e respostas cheguem após uma semana de silêncio. Se um
patch não estiver mais ativo no patchwork e a thread ficar inativa por mais de
uma semana - esclareça os próximos passos e/ou envie a próxima versão.

Especificamente para envios de RFC, se ninguém responder em uma semana  ou os
revisores perderam o envio ou não têm opiniões fortes a respeito. Se o código
estiver pronto, reenvie como um PATCH.

E-mails dizendo apenas "ping" ou "bump" são considerados rudes. Se você não
conseguir identificar o status do patch pelo patchwork ou onde a discussão
parou - descreva sua melhor suposição e pergunte se ela está correta. Por
exemplo::

  Não entendo quais são os próximos passos. A Pessoa X parece estar  descontente
  com A; devo fazer B e enviar novamente os patches?

.. _Solicitações de mudanças:

Mudanças solicitadas
~~~~~~~~~~~~~~~~~~~~

Patches marcados como ``Changes Requested`` precisam ser revisados. A nova
versão deve vir com um registro de alterações (changelog),
preferencialmente incluindo links para as postagens anteriores, por exemplo::

  [PATCH net-next v3] net: faz as vacas dizerem "muuu"

  Mesmo os usuários que não bebem leite apreciam ouvir as vacas dizendo
  "muuu".

  A quantidade de mugidos dependerá da taxa de pacotes, portanto, deve
  corresponder muito bem ao ciclo diurno.

  Signed-off-by: Joe Defarmer <joe@barn.org>
  ---
  v3:
    - adicionada uma nota sobre a flutuação do mugido conforme a hora
      do dia na
      mensagem de commit
  v2: https://lore.kernel.org/netdev/123themessageid@barn.org/
    - corrigido argumento ausente na kernel doc para netif_is_bovine()
    - corrigido vazamento de memória (memory leak) em
    netdev_register_cow()
  v1: https://lore.kernel.org/netdev/456getstheclicks@barn.org/

A mensagem de commit deve ser revisada para responder a quaisquer perguntas que
os revisores tenham feito em discussões anteriores. Ocasionalmente, a
atualização da mensagem de commit será a única mudança na nova versão.

Reenvios parciais
~~~~~~~~~~~~~~~~~

Por favor, sempre reenvie a série completa de patches e certifique-se de
numerar seus patches de forma que fique claro que este é o conjunto mais
recente e completo de patches que pode ser aplicado. Não tente reenviar apenas
os patches que foram alterados.

Lidando com patches aplicados incorretamente
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Ocasionalmente, uma série de patches é aplicada antes de receber feedback
crítico, ou a versão errada de uma série é aplicada.

Não é possível fazer o patch desaparecer uma vez que ele foi enviado (pushed);
o histórico de commits nas árvores netdev é imutável. Por favor, envie versões
incrementais sobre o que foi mesclado para corrigir os patches da maneira que
eles ficariam se a sua série de patches mais recente fosse mesclada.

Em casos onde uma reversão completa (revert) é necessária, a reversão deve ser
enviada como um patch para a lista com uma mensagem de commit explicando os
problemas técnicos com o commit revertido. Reversões devem ser usadas como
último recurso, quando a mudança original está completamente errada; correções
incrementais são preferidas.

Árvore estável
~~~~~~~~~~~~~~

Embora antigamente as submissões para a netdev não devessem carregar tags
explícitas ``CC: stable@vger.kernel.org``, esse não é mais o caso hoje em dia.
Por favor, siga as regras padrão de estabilidade em
``Documentation/process/stable-kernel-rules.rst``, e certifique-se de incluir as
tags Fixes apropriadas!

Correções de segurança
~~~~~~~~~~~~~~~~~~~~~~

Não envie e-mails diretamente para os mantenedores da netdev se você acha que
descobriu um bug que possa ter possíveis implicações de segurança. O atual
mantenedor da netdev tem solicitado consistentemente que as pessoas usem as
listas de discussão e não entrem em contato diretamente. Se você não estiver
de acordo com isso, considere enviar um e-mail para security@kernel.org ou
ler sobre http://oss-security.openwall.org/wiki/mailing-lists/distros como
possíveis mecanismos alternativos.

Envio conjunto de mudanças em componentes de espaço do usuário
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

O código de espaço do usuário (*user space*) que exercita funcionalidades do
kernel deve ser enviado juntamente com os patches do kernel. Isso dá aos
revisores a chance de ver como qualquer nova interface é usada e quão
bem ela funciona.

Quando as ferramentas de espaço do usuário residem no próprio repositório do
kernel, todas as alterações devem geralmente vir em uma única série. Se a série
se tornar muito grande ou se o projeto de espaço do usuário não for revisado na
netdev, inclua um link para um repositório público onde os patches de espaço do
usuário possam ser vistos.

No caso de ferramentas de espaço do usuário residirem em um repositório
separado, mas serem revisadas na netdev (por exemplo, patches para ferramentas
``iproute2``), os patches do kernel e do espaço do usuário devem formar séries
(threads) separadas quando postados na lista de discussão, por exemplo::

  [PATCH net-next 0/3] net: carta de apresentação de alguma funcionalidade
   └─ [PATCH net-next 1/3] net: preparação para alguma funcionalidade
   └─ [PATCH net-next 2/3] net: implementação de alguma funcionalidade
   └─ [PATCH net-next 3/3] selftest: net: alguma funcionalidade

  [PATCH iproute2-next] ip: adiciona suporte para alguma funcionalidade

A postagem em uma única thread é desencorajada porque confunde o patchwork
(a partir da versão 2.2.2 do patchwork).

Envio conjunto de selftests
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Os selftests devem fazer parte da mesma série que as mudanças de código.
Especificamente para correções, tanto a mudança de código quanto o teste
relacionado devem ir para a mesma árvore (os testes podem não ter uma tag
Fixes, o que é esperado). Misturar mudanças de código e mudanças de teste em
um único commit é desencorajado.

Preparando as mudanças
----------------------

Atenção aos detalhes é importante. Releia seu próprio trabalho como se você
fosse o revisor. Você pode começar usando o ``checkpatch.pl``, talvez até com
a flag ``--strict``. Mas não seja robótico e irracional ao fazer isso. Se sua
mudança for uma correção de bug, certifique-se de que seu log de commit indique
o sintoma visível para o usuário final, a razão subjacente de por que isso
acontece e, se necessário, explique por que a correção proposta é a melhor
maneira de resolver as coisas. Não corrompa espaços em branco e, como é comum,
não use recuos incorretos em argumentos de função que abrangem várias linhas.
Se for o seu primeiro patch, envie-o para si mesmo por e-mail para que você
possa testar a aplicação em uma árvore sem patches para confirmar que a
infraestrutura não o danificou.

Finalmente, volte e leia ``Documentation/process/submitting-patches.rst``
para ter certeza de que não está repetindo algum erro comum documentado lá.

Indicando a árvore de destino
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Para ajudar os mantenedores e os bots de CI, você deve marcar explicitamente
qual árvore seu patch tem como alvo. Supondo que você use git, utilize a flag
de prefixo::

  git format-patch --subject-prefix='PATCH net-next' inicio..fim

Use ``net`` em vez de ``net-next`` (sempre em letras minúsculas) no comando
acima para conteúdos de correção de bugs da árvore ``net``.

Dividindo o trabalho em patches
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Coloque-se no lugar do revisor. Cada patch é lido separadamente e, portanto,
deve constituir um passo compreensível em direção ao seu objetivo declarado.

Evite enviar séries com mais de 15 patches. Séries maiores levam mais tempo
para serem revisadas, pois os revisores adiarão a análise até encontrarem um
grande bloco de tempo disponível. Uma série pequena pode ser revisada em pouco
tempo, então os mantenedores simplesmente a revisam de imediato. Como resultado,
uma sequência de séries menores é mesclada mais rapidamente e com melhor
cobertura de revisão. Reenviar séries grandes também aumenta o tráfego na lista
de discussão.

Limitar patches pendentes na lista de discussão
-----------------------------------------------

Evite ter mais de 15 patches, em todas as séries, pendentes de revisão na lista
de discussão para uma única árvore. Em outras palavras, um máximo de 15 patches
sob revisão na ``net`` e um máximo de 15 patches sob revisão na ``net-next``.

Este limite tem o objetivo de focar o esforço do desenvolvedor nos testes dos
patches antes da revisão upstream, auxiliando a qualidade das submissões
upstream e aliviando a carga sobre os revisores.

Ordenação de variáveis locais ("árvore invertida", "RCS")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A netdev tem uma convenção para ordenar variáveis locais em funções. Ordene as
linhas de declaração de variáveis da mais longa para a mais curta, por exemplo::

  struct scatterlist *sg;
  struct sk_buff *skb;
  int err, i;

Se houver dependências entre as variáveis que impeçam a ordenação, mova a
inicialização para fora da linha de declaração.

Precedência de formatação
~~~~~~~~~~~~~~~~~~~~~~~~~

Ao trabalhar em código existente que utiliza formatação não padrão, faça com
que seu código siga as diretrizes mais recentes, para que, eventualmente,
todo o código no domínio da netdev esteja no formato preferido.

Uso de construções gerenciadas por dispositivo e cleanup.h
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Historicamente, a netdev permanece cética em relação às promessas de todas as
APIs de "auto-limpeza" (auto-cleanup), incluindo até mesmo os auxiliares
``devm_``. Eles não são o estilo preferido de implementação, apenas um estilo
aceitável.

O uso de ``guard()`` é desencorajado em qualquer função com mais de 20 linhas;
``scoped_guard()`` é considerado mais legível. O uso de lock/unlock normal
ainda é (levemente) preferido.

Construções de limpeza de baixo nível (como ``__free()``) podem ser usadas ao
construir APIs e auxiliares, especialmente iteradores com escopo. No entanto, o
uso direto de ``__free()`` dentro do núcleo de rede (networking core) e drivers
é desencorajado. Orientações semelhantes se aplicam à declaração de variáveis
no meio da função.

Patches de limpeza (Clean-up patches)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A netdev desencoraja patches que realizam limpezas simples que não estejam no
contexto de outro trabalho. Por exemplo:

* Tratar avisos do ``checkpatch.pl`` e outros avisos triviais de estilo de
  codificação
* Tratar problemas de Ordenação de variáveis locais
* Conversões para APIs gerenciadas por dispositivo (auxiliares ``devm_``)

Isso ocorre porque se considera que a agitação (*churn*) que tais mudanças
produzem tem um custo maior do que o valor de tais limpezas.

Por outro lado, correções de ortografia e gramática não são desencorajadas.

Reenviando após a revisão
~~~~~~~~~~~~~~~~~~~~~~~~~

Aguarde pelo menos 24 horas entre as postagens. Isso garantirá que revisores de
todas as localizações geográficas tenham a chance de se manifestar. Não espere
muito tempo (semanas) entre as postagens, pois isso tornará mais difícil para
os revisores lembrarem de todo o contexto.

Certifique-se de tratar todo o feedback em sua nova postagem. Não envie uma
nova versão do código se a discussão sobre a versão anterior ainda estiver em
andamento, a menos que seja instruído diretamente por um revisor.

A nova versão dos patches deve ser postada como uma thread separada, não como
uma resposta à postagem anterior. O registro de alterações (changelog) deve
incluir um link para a postagem anterior (veja :ref:`Solicitações
de mudanças`).

Testes
------

Nível de teste esperado
~~~~~~~~~~~~~~~~~~~~~~~

No mínimo, suas alterações devem passar por uma compilação ``allyesconfig`` e
uma ``allmodconfig`` com ``W=1`` definido, sem novos avisos ou falhas.

O ideal é que você tenha feito testes em tempo de execução específicos para sua
alteração, e que a série de patches contenha um conjunto de selftests do kernel
para ``tools/testing/selftests/net`` ou usando o framework KUnit.

Espera-se que você teste suas alterações no topo da árvore de rede relevante
(``net`` ou ``net-next``) e não, por exemplo, em uma árvore estável ou na
``linux-next``.

Verificações do patchwork
~~~~~~~~~~~~~~~~~~~~~~~~~

As verificações (*checks*) no patchwork são, em sua maioria, wrappers simples
em torno de scripts existentes do kernel; as fontes estão disponíveis em:

https://github.com/linux-netdev/nipa/tree/master/tests

**Não** envie seus patches apenas para executá-los nas verificações. Você deve
garantir que seus patches estejam prontos, testando-os localmente antes de
postar na lista de discussão. A instância do bot de build do patchwork fica
sobrecarregada com muita facilidade e a netdev@vger realmente não precisa de
mais tráfego se pudermos evitar.

netdevsim
~~~~~~~~~

O ``netdevsim`` é um driver de teste que pode ser usado para exercitar APIs de
configuração de driver sem a necessidade de hardware compatível. Mock-ups e
testes baseados no ``netdevsim`` são fortemente encorajados ao adicionar novas
APIs, mas o ``netdevsim`` em si **não** é considerado um caso de uso/usuário.
Você também deve implementar as novas APIs em um driver real.

Não damos garantias de que o ``netdevsim`` mudará no futuro de uma forma que
quebraria o que normalmente seria considerado uAPI.  O ``netdevsim`` é reservado
apenas para uso por testes upstream, portanto, quaisquer novos recursos do
``netdevsim`` devem ser acompanhados de selftests em ``tools/testing/selftests/``.

Status de suporte para drivers
------------------------------

.. note:

Os requisitos a seguir aplicam-se apenas a drivers de NIC Ethernet.

A netdev define requisitos adicionais para drivers que desejam adquirir o status
``Supported`` (Suportado) no arquivo MAINTAINERS. Drivers ``Supported`` devem
executar todos os testes de driver upstream e relatar os resultados duas vezes
por dia. Drivers que não cumprirem este requisito devem usar o status
``Maintained`` (Mantido). Atualmente, não há diferença na forma como os drivers
``Supported`` e ``Maintained`` são tratados no upstream.

As regras exatas que um driver deve seguir para adquirir o status ``Supported``:

1. Deve executar todos os testes sob os alvos ``drivers/net`` e
   ``drivers/net/hw`` dos selftests do Linux. A execução e o relato
   de testes privados / internos também são bem-vindos, mas os testes
   upstream são obrigatórios.

2. A frequência mínima de execução é uma vez a cada 12 horas. Deve
   testar o branch designado a partir do feed de branches selecionado.
   Observe que os branches são construídos automaticamente e estão
   expostos à postagem intencional de patches maliciosos; portanto,
   os sistemas de teste devem ser isolados.

3. Drivers que suportam múltiplas gerações de dispositivos devem
   testar pelo menos um dispositivo de cada geração. Um manifesto do
   ambiente de teste (*testbed manifest* - formato exato a definir)
   deve descrever os modelos de dispositivos testados.

4. Os testes devem ser executados de forma confiável; se múltiplos
   branches forem ignorados ou se os testes falharem devido a problemas
   no ambiente de execução, o status ``Supported`` será retirado.

5. Falhas nos testes devido a bugs no driver ou no próprio teste,
   ou falta de suporte para a funcionalidade que o teste visa, *não*
   são motivo para a perda do status ``Supported``.

O CI da netdev manterá uma página oficial de dispositivos suportados, listando
seus resultados de testes recentes.

O mantenedor do driver pode providenciar para que outra pessoa execute o teste;
não há exigência de que a pessoa listada como mantenedora (ou seu empregador)
seja responsável pela execução dos testes. Colaborações entre
fornecedores, hospedagem de CI no GitHub (GH CI), outros repositórios sob o
linux-netdev, etc., são muito bem-vindas.

Veja https://github.com/linux-netdev/nipa/wiki para mais informações sobre o CI
da netdev. Sinta-se à vontade para entrar em contato com os mantenedores ou com
a lista para quaisquer dúvidas.

Orientações para revisores
--------------------------

Revisar patches de outras pessoas na lista é altamente incentivado,
independentemente do nível de experiência. Para orientações gerais e dicas
úteis, consulte `revisão de tópicos avançados de desenvolvimento`.

É seguro assumir que os mantenedores da netdev conhecem a comunidade e o nível
de experiência dos revisores. Os revisores não devem se preocupar com o fato de
seus comentários impedirem ou desviarem o fluxo de patches. Revisores menos
experientes são fortemente incentivados a fazer uma revisão mais aprofundada das
submissões e não focar exclusivamente em questões triviais ou subjetivas, como
formatação de código, tags, etc.

Depoimentos / feedback
----------------------

Algumas empresas utilizam o feedback de colegas em revisões de desempenho de
funcionários. Sinta-se à vontade para solicitar feedback dos mantenedores da
netdev, especialmente se você dedica uma quantidade significativa de tempo
revisando código e se esforça além do esperado para melhorar a infraestrutura
compartilhada.

O feedback deve ser solicitado por você, o colaborador, e será sempre
compartilhado com você (mesmo que você solicite que ele seja enviado ao seu
gerente).