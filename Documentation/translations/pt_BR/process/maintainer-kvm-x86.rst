.. SPDX-License-Identifier: GPL-2.0

KVM x86
=======

Prefácio
--------

O KVM se esforça para ser uma comunidade acolhedora; as contribuições de
recém-chegados são valorizadas e incentivadas. Por favor, não se sinta
desanimado ou intimidado pela extensão deste documento e pelas muitas
regras/diretrizes que ele contém. Todo mundo comete erros e todo mundo já foi um
novato em algum momento. Desde que você faça um esforço honesto para seguir as
diretrizes do KVM x86, seja receptivo ao feedback e aprenda com os erros que
cometer, você será recebido de braços abertos, não com tochas e forquilhas.

(TL;DR)
--------
Testes são obrigatórios. Seja consistente com os estilos e padrões estabelecidos.

Árvores
-------
O KVM x86 está atualmente em um período de transição: deixando de fazer parte da
árvore principal do KVM para se tornar "apenas mais uma arquitetura KVM". Como tal,
o KVM x86 está dividido entre a árvore principal do KVM,
``git.kernel.org/pub/scm/virt/kvm/kvm.git``, e uma árvore específica para KVM x86,
``github.com/kvm-x86/linux.git``.

De modo geral, as correções (fixes) para o ciclo atual são aplicadas diretamente
na árvore principal do KVM, enquanto todo o desenvolvimento para o próximo ciclo
é roteado através da árvore do KVM x86. No caso improvável de uma correção para o
ciclo atual ser roteada através da árvore do KVM x86, ela será aplicada à branch
``fixes`` antes de seguir para a árvore principal do KVM.

Note que espera-se que este período de transição dure bastante tempo, ou seja,
será o status quo em um futuro próximo.

Branches
~~~~~~~~
A árvore do KVM x86 é organizada em múltiplas branches de tópicos (topic
branches). O objetivo de usar branches de tópicos mais granulares é facilitar o
acompanhamento de uma área específica de desenvolvimento e limitar os danos
colaterais de erros humanos e/ou commits com bugs; por exemplo, descartar o
commit HEAD de uma branch de tópico não tem impacto nos hashes SHA1 de outros
commits em andamento, e a necessidade de rejeitar um pull request devido a bugs
atrasa apenas aquela branch de tópico específica.

Todas as branches de tópicos, exceto a ``next`` e a ``fixes``, são incorporadas
na ``next`` via um "Cthulhu merge" conforme a necessidade, ou seja, sempre que
uma branch de tópico é atualizada. Como resultado, force pushes para a branch
``next`` são comuns.

Ciclo de Vida
~~~~~~~~~~~~~
As correções (fixes) destinadas ao lançamento atual, também conhecido como
mainline, são normalmente aplicadas diretamente na árvore principal do KVM, ou
seja, não passam pela árvore do KVM x86.

As mudanças destinadas ao próximo lançamento são roteadas através da árvore do
KVM x86. Pull requests (do KVM x86 para o KVM principal) são enviados para cada
branch de tópico do KVM x86, normalmente na semana anterior à abertura da janela
de merge por Linus, por exemplo, na semana seguinte ao rc7 para lançamentos
"normais". Se tudo correr bem, as branches de tópicos são incorporadas ao pull
request principal do KVM enviado durante a janela de merge de Linus.

A árvore do KVM x86 não possui sua própria janela de merge oficial, mas há um
"soft close" (fechamento flexível) por volta do rc5 para novos recursos, e um
"soft close" por volta do rc6 para correções (para o próximo lançamento; veja
acima para correções destinadas ao lançamento atual).

Cronograma
----------
As submissões são normalmente revisadas e aplicadas em ordem FIFO (primeiro a
entrar, primeiro a sair), com alguma margem de manobra para o tamanho de uma
série, patches que estão "cache hot", etc. Correções (fixes), especialmente para
o lançamento atual e/ou árvores estáveis (stable trees), têm prioridade na fila.
Patches que serão aceitos através de uma árvore não-KVM (mais frequentemente
através da árvore "tip") e/ou que possuam outros "acks"/revisões também ganham
certa prioridade.

Note que a grande maioria das revisões é feita entre o rc1 e o rc6,
aproximadamente. O período entre o rc6 e o próximo rc1 é usado para colocar
outras tarefas em dia, ou seja, o "silêncio de rádio" durante este período não é
incomum.

Pings para obter uma atualização de status são bem-vindos, mas tenha em mente o
tempo do ciclo de lançamento atual e tenha expectativas realistas. Se você está
dando um ping para aceitação — ou seja, não apenas para feedback ou uma
atualização — por favor, faça tudo o que puder, dentro do razoável, para garantir
que seus patches estejam prontos para o merge! Pings em séries que quebram o
build ou falham em testes resultam em mantenedores infelizes!

Desenvolvimento
---------------

Árvore/Branch Base
~~~~~~~~~~~~~~~~~~
Correções destinadas ao lançamento atual, também conhecido como mainline, devem
ser baseadas em ``git://git.kernel.org/pub/scm/virt/kvm/kvm.git master``. Note
que as correções não garantem inclusão automática no lançamento atual. Não
existe uma regra única, mas tipicamente apenas correções para bugs que sejam
urgentes, críticos e/ou que tenham sido introduzidos no lançamento atual devem
ser destinadas ao lançamento atual.

Todo o restante deve ser baseado em ``kvm-x86/next``, ou seja, não há
necessidade de selecionar uma branch de tópico específica como base. Se houver
conflitos e/ou dependências entre as branches de tópicos, é trabalho do
mantenedor resolvê-los.

A única exceção ao uso da ``kvm-x86/next`` como base é se um patch/série for uma
série multi-arquitetura (multi-arch), ou seja, possuir modificações não triviais
no código comum do KVM e/ou possuir mudanças mais do que superficiais no código
de outras arquiteturas. Patches/séries multi-arquitetura devem, em vez disso,
ser baseados em um ponto comum e estável no histórico do KVM, por exemplo, o
release candidate no qual a ``kvm-x86 next`` se baseia. Se você não tiver
certeza se um patch/série é verdadeiramente multi-arquitetura, erre pelo lado da
cautela e trate-o como tal, ou seja, use uma base comum.

Estilo de Codificação
~~~~~~~~~~~~~~~~~~~~~
Quando se trata de estilo, nomenclatura, padrões, etc., a consistência é a
prioridade número um no KVM x86. Se tudo mais falhar, siga o que já existe.

Com algumas ressalvas listadas abaixo, siga o estilo de codificação preferido
dos mantenedores da árvore "tip" (:ref:`maintainer-tip-coding-style`), já que
patches/séries frequentemente tocam tanto arquivos do KVM quanto arquivos x86
não-KVM, ou seja, atraem a atenção de mantenedores do KVM *e* da árvore "tip".

O uso de "reverse fir tree" (árvore de abeto invertida), também conhecido como
"árvore de Natal invertida", para declarações de variáveis não é estritamente
obrigatório, embora ainda seja preferido.

Exceto por alguns casos excepcionais, não use comentários kernel-doc para
funções. A grande maioria das funções "públicas" do KVM não são verdadeiramente
públicas, pois se destinam apenas ao consumo interno do KVM (há planos para
privatizar os headers e exports do KVM para reforçar isso).

Comentários
~~~~~~~~~~~
Escreva comentários usando o modo imperativo e evite pronomes. Use comentários
para fornecer uma visão geral de alto nível do código e/ou para explicar por
que o código faz o que faz. Não reitere o que o código faz literalmente; deixe
o código falar por si mesmo. Se o código em si for inescrutável, comentários
não ajudarão.

Referências ao SDM e ao APM
~~~~~~~~~~~~~~~~~~~~~~~~~~~
Grande parte da base de código do KVM está diretamente ligada ao comportamento
arquitetural definido no Manual de Desenvolvimento de Software (SDM) da Intel e
no Manual do Programador de Arquitetura (APM) da AMD. O uso de "Intel SDM" e
"AMD APM", ou até mesmo apenas "SDM" ou "APM", sem contexto adicional, é
perfeitamente aceitável.

Não faça referência a seções, tabelas, figuras, etc., por número, especialmente
em comentários. Em vez disso, se necessário (veja abaixo), copie e cole o trecho
relevante e referencie seções/tabelas/figuras pelo nome. Os layouts do SDM e do
APM mudam constantemente e, portanto, os números/rótulos não são estáveis.

De modo geral, não faça referência explícita nem copie e cole do SDM ou do APM
em comentários. Com poucas exceções, o KVM *deve* respeitar o comportamento
arquitetural; portanto, subentende-se que o comportamento do KVM está emulando o
comportamento do SDM e/ou do APM. Note que fazer referência ao SDM/APM em
changelogs para justificar a mudança e fornecer contexto é perfeitamente
aceitável e incentivado.

Shortlog
~~~~~~~~
O formato de prefixo preferencial é ``KVM: <topic>:``, onde ``<topic>`` é um dos
seguintes::

  - x86
  - x86/mmu
  - x86/pmu
  - x86/xen
  - selftests
  - SVM
  - nSVM
  - VMX
  - nVMX

**NÃO use x86/kvm!** ``x86/kvm`` é usado exclusivamente para mudanças no Linux
como convidado (guest) de um KVM, ou seja, para ``arch/x86/kernel/kvm.c``. Não
use nomes de arquivos ou caminhos completos de arquivos como prefixo do
assunto/shortlog.

Note que estes não se alinham com as branches de tópicos (as branches de tópicos
se preocupam muito mais com conflitos de código).

Todos os nomes são sensíveis a maiúsculas e minúsculas! ``KVM: x86:`` é bom,
``kvm: vmx:`` não é.

Comece com letra maiúscula a primeira palavra da descrição condensada do patch,
mas omita a pontuação final. Ex.::

    KVM: x86: Fix a null pointer dereference in function_xyz()

e não::

    kvm: x86: fix a null pointer dereference in function_xyz.

Se um patch tocar em múltiplos tópicos, suba na árvore conceitual para encontrar
o primeiro pai comum (que geralmente é apenas ``x86``). Em caso de dúvida,
``git log caminho/do/arquivo`` deve fornecer uma dica razoável.

Novos tópicos surgem ocasionalmente, mas, por favor, inicie uma discussão na
lista se desejar propor a introdução de um novo tópico; ou seja, não aja por
conta própria.

Veja :ref:`the_canonical_patch_format` para mais informações, com uma ressalva:
não trate o limite de 70-75 caracteres como um limite absoluto e rígido. Em
vez disso, use 75 caracteres como um limite firme, mas não rígido, e use 80
caracteres como um limite intransponível. Ou seja, permita que o shortlog
ultrapasse alguns caracteres do limite padrão se você tiver um bom motivo para
fazê-lo.

Changelog
~~~~~~~~~
O mais importante: escreva os changelogs usando o modo imperativo e evite o uso
de pronomes.

Veja :ref:`describe_changes` para mais informações, com uma ressalva: comece com
uma breve descrição das mudanças reais e, em seguida, apresente o contexto e o
histórico. Note! Esta ordem entra em conflito direto com a abordagem preferida
da árvore "tip"! Por favor, siga o estilo preferido da árvore "tip" ao enviar
patches que visam primariamente o código de arch/x86 que _NÃO_ seja código KVM.

Declarar o que um patch faz antes de mergulhar nos detalhes é preferido pelo KVM
x86 por vários motivos. Primeiro e mais importante, qual código está sendo
realmente alterado é, reconhecidamente, a informação mais importante e,
portanto, essa informação deve ser fácil de encontrar. Changelogs que escondem
"o que está mudando de fato" em uma única linha após 3 ou mais parágrafos de
histórico tornam muito difícil encontrar essa informação.

Para uma revisão inicial, pode-se argumentar que "o que está quebrado" é mais
importante, mas para uma leitura rápida de logs e arqueologia do git, os
detalhes minuciosos importam cada vez menos. Por exemplo, ao fazer uma série de
"git blame", os detalhes de cada mudança ao longo do caminho são inúteis; os
detalhes só importam para o culpado. Fornecer "o que mudou" facilita determinar
rapidamente se um commit pode ou não ser de interesse.

Outro benefício de declarar "o que está mudando" primeiro é que quase sempre é
possível declarar "o que está mudando" em uma única frase. Por outro lado,
exceto pelos bugs mais simples, todos exigem várias frases ou parágrafos para
descrever totalmente o problema. Se tanto "o que está mudando" quanto "qual é o
bug" forem super curtos, a ordem não importa. Mas se um for mais curto (quase
sempre o "o que está mudando"), então cobrir o mais curto primeiro é vantajoso
porque é menos inconveniente para leitores/revisores que têm uma preferência de
ordenação estrita. Ex: ter que pular uma frase para chegar ao contexto é menos
doloroso do que ter que pular três parágrafos para chegar ao "o que está
mudando".

Correções (Fixes)
~~~~~~~~~~~~~~~~~
Se uma mudança corrige um bug do KVM/kernel, adicione uma tag Fixes:, mesmo que
a mudança não precise ser portada (backported) para kernels estáveis, e mesmo
que a mudança corrija um bug em uma versão mais antiga.

Por outro lado, se uma correção realmente precisar de backport, marque
explicitamente o patch com "Cc: stable@vger.kernel.org" (embora o e-mail em si
não precise enviar cópia para a lista stable); o KVM x86 opta por não realizar
o backport automático de tags Fixes: por padrão. Alguns patches selecionados
automaticamente são portados, mas exigem aprovação explícita do mantenedor
(pesquise por MANUALSEL).

Referências a Funções
~~~~~~~~~~~~~~~~~~~~~
Quando uma função for mencionada em um comentário, changelog ou shortlog (ou em
qualquer outro lugar, aliás), use o formato ``nome_da_funcao()``. Os parênteses
fornecem contexto e removem a ambiguidade da referência.

Testes
------
No mínimo, *todos* os patches de uma série devem compilar sem erros para
KVM_INTEL=m, KVM_AMD=m e KVM_WERROR=y. Compilar cada combinação possível de
Kconfigs não é viável, mas quanto mais, melhor. KVM_SMM, KVM_XEN, PROVE_LOCKING
e X86_64 são opções (knobs) particularmente interessantes para se testar.

A execução de KVM selftests e KVM-unit-tests também é obrigatória (e, para
afirmar o óbvio, os testes precisam passar). A única exceção é para mudanças
que tenham probabilidade insignificante de afetar o comportamento em tempo de
execução, por exemplo, patches que apenas modificam comentários. Sempre que
possível e relevante, o teste tanto em Intel quanto em AMD é fortemente
preferido. A inicialização de uma VM real é incentivada, mas não obrigatória.

Para mudanças que tocam o código de shadow paging do KVM, executar com o TDP
(EPT/NPT) desabilitado é obrigatório. Para mudanças que afetam o código comum da
MMU do KVM, a execução com o TDP desabilitado é fortemente incentivada. Para
todas as outras mudanças, se o código sendo modificado depender de e/ou
interagir com um parâmetro de módulo (module param), o teste com as
configurações relevantes é obrigatório.

Note que o KVM selftests e o KVM-unit-tests possuem falhas conhecidas. Se você
suspeitar que uma falha não se deve às suas alterações, verifique se a *exata
mesma* falha ocorre com e sem as suas mudanças.

Mudanças que tocam a documentação em reStructuredText, ou seja, arquivos .rst,
devem compilar o htmldocs de forma limpa, ou seja, sem novos avisos (warnings)
ou erros.

Se você não puder testar totalmente uma mudança, por exemplo, devido à falta de
hardware, declare claramente qual nível de teste você foi capaz de realizar,
por exemplo, na cover letter (carta de apresentação).

Novos Recursos
~~~~~~~~~~~~~~
Com uma exceção, novos recursos *devem* vir acompanhados de cobertura de testes.
Testes específicos do KVM não são estritamente obrigatórios, por exemplo, se a
cobertura for fornecida ao executar uma VM convidada (guest) suficientemente
habilitada, ou ao executar um selftest de kernel relacionado em uma VM, mas
testes dedicados do KVM são preferidos em todos os casos. Casos de teste
negativos, em particular, são obrigatórios para a habilitação de novos recursos
de hardware, já que fluxos de erro e exceção raramente são exercitados
simplesmente ao executar uma VM.

A única exceção a esta regra é se o KVM estiver simplesmente anunciando suporte
para um recurso via KVM_GET_SUPPORTED_CPUID, ou seja, para instruções/recursos
que o KVM não pode impedir um convidado de usar e para os quais não há uma
habilitação real.

Note que "novos recursos" não significa apenas "novos recursos de hardware"!
Novos recursos que não podem ser bem validados usando os KVM selftests e/ou
KVM-unit-tests existentes devem vir acompanhados de testes.

Enviar o desenvolvimento de novos recursos sem testes para obter feedback
antecipado é mais do que bem-vindo, mas tais submissões devem ser marcadas como
RFC, e a carta de apresentação (cover letter) deve declarar claramente que tipo
de feedback é solicitado/esperado. Não abuse do processo de RFC; as RFCs
normalmente não receberão uma revisão profunda.

Correções de Bugs
~~~~~~~~~~~~~~~~~
Exceto por bugs "óbvios" encontrados por inspeção, as correções devem vir
acompanhadas de um reprodutor (reproducer) para o bug que está sendo corrigido.
Em muitos casos, o reprodutor é implícito, por exemplo, para erros de build e
falhas de teste, mas ainda assim deve estar claro para os leitores o que está
quebrado e como verificar a correção. Alguma margem de manobra é dada para
bugs encontrados através de cargas de trabalho ou testes não públicos, mas a
disponibilização de testes de regressão para tais bugs é fortemente preferida.

Em geral, testes de regressão são preferidos para qualquer bug que não seja
trivial de ser atingido. Por exemplo, mesmo que o bug tenha sido originalmente
encontrado por um fuzzer como o syzkaller, um teste de regressão direcionado
pode ser justificável se o bug exigir que se atinja uma condição de corrida do
tipo "uma em um milhão".

Note que os bugs do KVM raramente são urgentes *e* não triviais de reproduzir.
Pergunte a si mesmo se um bug é realmente o fim do mundo antes de enviar uma
correção sem um reprodutor.

Postagem
--------

Links
~~~~~
Não faça referência explícita a relatórios de bugs, versões anteriores de um
patch/série, etc., através de cabeçalhos ``In-Reply-To:``. O uso de
``In-Reply-To:`` torna-se uma bagunça infernal para grandes séries e/ou quando
o número de versões aumenta, e o ``In-Reply-To:`` é inútil para qualquer
pessoa que não tenha a mensagem original, por exemplo, se alguém não estava
em cópia (Cc) no relatório do bug ou se a lista de destinatários mudar entre
as versões.

Para vincular a um relatório de bug, versão anterior ou qualquer coisa de
interesse, use links do lore. Para referenciar versão(ões) anterior(es), de modo
geral, não inclua um Link: no changelog, pois não há necessidade de registrar o
histórico no git; ou seja, coloque o link na carta de apresentação (cover
letter) ou na seção que o git ignora. Forneça um Link: formal para relatórios
de bugs e/ou discussões que levaram ao patch. O contexto de por que uma mudança
foi feita é altamente valioso para futuros leitores.

Base do Git (Git Base)
~~~~~~~~~~~~~~~~~~~~~~
Se você estiver usando o git versão 2.9.0 ou posterior (Googlers, isso inclui
todos vocês!), use ``git format-patch`` com a flag ``--base`` para incluir
automaticamente as informações da árvore base nos patches gerados.

Note que ``--base=auto`` funciona conforme o esperado se, e somente se, o
upstream de uma branch estiver definido para a branch de tópico base; por
exemplo, ele fará a coisa errada se o seu upstream estiver definido para o seu
repositório pessoal para fins de backup. Uma solução "auto" alternativa é
derivar os nomes das suas branches de desenvolvimento com base no seu tópico
KVM x86 e alimentar isso no ``--base``. Por exemplo,
``x86/pmu/minha_branch`` e, em seguida, escrever um pequeno wrapper para
extrair ``pmu`` do nome da branch atual para resultar em ``--base=x/pmu``, onde
``x`` é o nome que seu repositório usa para rastrear o remoto do KVM x86.

Postagem Conjunta de Testes
~~~~~~~~~~~~~~~~~~~~~~~~~~~
KVM selftests que estão associados a mudanças no KVM, por exemplo, testes de
regressão para correções de bugs, devem ser postados junto com as mudanças do
KVM como uma única série. As regras padrão do kernel para bissecção (bisection)
se aplicam, ou seja, mudanças no KVM que resultem em falhas de teste devem ser
ordenadas após as atualizações dos selftests e, vice-versa, novos testes que
falhem devido a bugs do KVM devem ser ordenados após as correções do KVM.

KVM-unit-tests devem *sempre* ser postados separadamente. Ferramentas, como o
b4 am, não sabem que o KVM-unit-tests é um repositório separado e ficam
confusas quando os patches de uma série se aplicam a árvores diferentes. Para
vincular os patches do KVM-unit-tests aos patches do KVM, poste primeiro as
mudanças do KVM e, em seguida, forneça um link do lore para o patch/série do
KVM no(s) patch(es) do KVM-unit-tests.

Notificações
------------
Quando um patch/série é oficialmente aceito, um e-mail de notificação será
enviado em resposta à postagem original (carta de apresentação para séries de
múltiplos patches). A notificação incluirá a árvore e a branch de tópico,
juntamente com os SHA1s dos commits dos patches aplicados.

Se um subconjunto de patches for aplicado, isso será claramente declarado na
notificação. A menos que seja dito o contrário, subentende-se que quaisquer
patches na série que não foram aceitos precisam de mais trabalho e devem ser
enviados em uma nova versão.

Se, por algum motivo, um patch for descartado após ter sido oficialmente
aceito, uma resposta será enviada ao e-mail de notificação explicando o porquê
do descarte, bem como os próximos passos.

Estabilidade de SHA1
~~~~~~~~~~~~~~~~~~~~
Os SHA1s não têm garantia de serem 100% estáveis até que cheguem na árvore do
Linus! Um SHA1 é *geralmente* estável uma vez que a notificação tenha sido
enviada, mas imprevistos acontecem. Na maioria dos casos, uma atualização no
e-mail de notificação será fornecida se o SHA1 de um patch aplicado mudar. No
entanto, em alguns cenários, por exemplo, se todas as branches do KVM x86
precisarem de rebase, as notificações individuais não serão enviadas.

Vulnerabilidades
----------------
Bugs que podem ser explorados pelo convidado (guest) para atacar o hospedeiro
(host) (kernel ou espaço do usuário), ou que podem ser explorados por uma VM
aninhada (nested) contra o *seu* próprio hospedeiro (L2 atacando L1), são de
interesse particular para o KVM. Por favor, siga o protocolo em
:ref:`securitybugs` se você suspeitar que um bug possa levar a um escape,
vazamento de dados, etc.
